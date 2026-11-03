cmake_minimum_required(VERSION 3.16)
project(Stitching LANGUAGES CXX)

include(CTest)
include(cmake/Dependencies.cmake)
include(cmake/Backend.cmake)
include(cmake/Frontend.cmake)

add_executable(stitching src/main.cpp)
target_link_libraries(stitching PRIVATE stitching_backend)

include(cmake/Tests.cmake)
include(cmake/Install.cmake)
