project(Hypnos-Tests CXX)

add_executable(Hypnos-Tests "Main.cpp")

set_target_properties(Hypnos-Tests PROPERTIES LINKER_LANGUAGE CXX)

target_link_libraries(Hypnos-Tests PRIVATE Hypnos)