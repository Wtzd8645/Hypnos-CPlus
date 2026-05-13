cmake_minimum_required (VERSION 3.12)

project(Hypnos VERSION 0.0.1 LANGUAGES CXX)

include(CTest)
include(GNUInstallDirs)

add_subdirectory("packages/mimalloc")
add_subdirectory("packages/Hypnos-Core")
add_subdirectory("runtime")
add_subdirectory("tests")
