cmake_minimum_required (VERSION 3.12)

project(Hypnos VERSION 0.0.1 LANGUAGES CXX)

include(CTest)

add_subdirectory("Packages/mimalloc")
add_subdirectory("Packages/Hypnos-Core")
add_subdirectory("Runtime")
add_subdirectory("Tests")
