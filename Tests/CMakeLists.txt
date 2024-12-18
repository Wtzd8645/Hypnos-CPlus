project(Hypnos-Tests)

add_executable(Hypnos-Tests "Program.cpp")

set_target_properties(Hypnos-Tests PROPERTIES LINKER_LANGUAGE CXX)

add_dependencies(Hypnos-Tests Hypnos)

target_include_directories(Hypnos-Tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(Hypnos-Tests PRIVATE Hypnos)