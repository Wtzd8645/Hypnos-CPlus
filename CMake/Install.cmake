install(TARGETS Hypnos
    EXPORT HypnosTargets
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(DIRECTORY "${PROJECT_SOURCE_DIR}/Include/" DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(EXPORT HypnosTargets
    FILE HypnosTargets.cmake
    NAMESPACE Blanketmen::
    DESTINATION lib/cmake/Hypnos
)

include(CMakePackageConfigHelpers)
configure_package_config_file(
    "${PROJECT_SOURCE_DIR}/CMake/HypnosConfig.cmake.in"
    "${PROJECT_BINARY_DIR}/HypnosConfig.cmake"
    INSTALL_DESTINATION lib/cmake/Hypnos
)

write_basic_package_version_file(
    "${PROJECT_BINARY_DIR}/HypnosConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMinorVersion
)

install(FILES
    "${PROJECT_BINARY_DIR}/HypnosConfig.cmake"
    "${PROJECT_BINARY_DIR}/HypnosConfigVersion.cmake"
    DESTINATION lib/cmake/Hypnos
)
