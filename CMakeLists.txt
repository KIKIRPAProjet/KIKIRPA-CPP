cmake_minimum_required(VERSION 3.16)
project(Stitching LANGUAGES CXX)

if(APPLE)
    foreach(homebrew_prefix IN ITEMS "/opt/homebrew/opt/qtbase" "/usr/local/opt/qtbase")
        if(EXISTS "${homebrew_prefix}/lib/cmake/Qt6/Qt6Config.cmake")
            list(PREPEND CMAKE_PREFIX_PATH "${homebrew_prefix}")
            break()
        endif()
    endforeach()
endif()

include(cmake/ClangTidy.cmake)
include(CTest)
include(cmake/Dependencies.cmake)
include(cmake/Backend.cmake)
include(cmake/Frontend.cmake)

add_executable(stitching src/main.cpp)
target_link_libraries(stitching PRIVATE stitching_backend)

include(cmake/Tests.cmake)
include(cmake/Install.cmake)
