add_executable(Hypnos-Tests "Main.cpp")

add_dependencies(Hypnos-Tests Hypnos)

target_link_libraries(Hypnos-Tests PRIVATE Hypnos)

if (BUILD_TESTING)
    add_test(NAME Hypnos-Tests COMMAND Hypnos-Tests 1)
    set_tests_properties(Hypnos-Tests PROPERTIES LABELS "first-party")
endif ()
