cmake_minimum_required(VERSION 3.22)

macro(add_finder_test test_name test_source)
    add_executable(${test_name} ${test_source})
    target_link_libraries(${test_name} PRIVATE
        finder_core
        Qt6::Core
        Qt6::Test
    )
    add_test(NAME ${test_name} COMMAND ${test_name})
endmacro()

add_finder_test(test_fileitem test_fileitem.cpp)
add_finder_test(test_operations test_operations.cpp)
add_finder_test(test_xdgtrash test_xdgtrash.cpp)
add_finder_test(test_search test_search.cpp)
add_finder_test(test_undo test_undo.cpp)
add_finder_test(test_tags test_tags.cpp)
add_finder_test(test_favorites test_favorites.cpp)
add_finder_test(test_archives test_archives.cpp)
add_finder_test(test_volumemanager test_volumemanager.cpp)
add_finder_test(test_desktopapps test_desktopapps.cpp)
add_finder_test(test_servicemenus test_servicemenus.cpp)
add_finder_test(test_recentfiles test_recentfiles.cpp)

add_executable(test_selection test_selection.cpp)
target_link_libraries(test_selection PRIVATE
    finder_ui
    finder_core
    Qt6::Widgets
    Qt6::Test
)
add_test(NAME test_selection COMMAND test_selection)
