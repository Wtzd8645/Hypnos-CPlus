add_executable(Hypnos-Tests "Main.cpp")

add_dependencies(Hypnos-Tests Hypnos)

target_link_libraries(Hypnos-Tests PRIVATE Hypnos)