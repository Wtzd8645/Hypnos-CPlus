cmake_minimum_required (VERSION 3.12)

project(Hypnos VERSION 0.0.1 LANGUAGES CXX)

include(CTest)
include(GNUInstallDirs)
include("${CMAKE_CURRENT_SOURCE_DIR}/CMake/CompilerOptions.cmake")

add_subdirectory("Packages/mimalloc")
add_subdirectory("Packages/Hypnos-Core")
add_subdirectory("Source/Runtime")

if (BUILD_TESTING)
    add_subdirectory("Source/Tests")
endif()

if (CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
    include("${CMAKE_CURRENT_SOURCE_DIR}/CMake/Install.cmake")
endif()
