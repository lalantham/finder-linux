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
