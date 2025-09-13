add_executable(Hypnos-Tests "Main.cpp")

add_dependencies(Hypnos-Tests Hypnos-Runtime)

set_target_properties(Hypnos-Tests PROPERTIES LINKER_LANGUAGE CXX)

target_compile_options(Hypnos-Tests PRIVATE -stdlib=libc++)

target_link_options(Hypnos-Tests PRIVATE -stdlib=libc++)

target_link_libraries(Hypnos-Tests PRIVATE Hypnos-Runtime)