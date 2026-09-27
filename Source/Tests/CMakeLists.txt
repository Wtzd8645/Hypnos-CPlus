add_executable(Hypnos-Tests "Main.cpp")

add_dependencies(Hypnos-Tests Hypnos)

target_include_directories(Hypnos-Tests PRIVATE "${PROJECT_SOURCE_DIR}/Source/Runtime")

target_link_libraries(Hypnos-Tests PRIVATE Hypnos)

add_test(NAME Hypnos-Tests COMMAND Hypnos-Tests 1)
set_tests_properties(Hypnos-Tests PROPERTIES LABELS "first-party")
