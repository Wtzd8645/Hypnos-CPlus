cmake_minimum_required (VERSION 3.12)

project(Hypnos VERSION 0.0.1 LANGUAGES CXX)

if (UNIX)
    set(PLATFORM_IMPL_FILES
        "Network/NetworkUtils_Unix.cpp" "Network/TcpServer_Unix.cpp")
endif ()

add_library(Hypnos STATIC ${PLATFORM_IMPL_FILES}
    "Network/NetworkManager.cpp" "Network/ServerSocketBase.cpp"
    "GameData/GameDataManager.cpp" "GameData/I18nTextManager.cpp"
    "GameState/GameStateManager.cpp"
    "GameTime/GameTimeManager.cpp" "GameTime/CountdownTimer.cpp" "GameTime/LapTimer.cpp")

target_compile_features(Hypnos PRIVATE cxx_std_20)

target_compile_options(Hypnos PRIVATE -Wall -Wextra)

target_include_directories(Hypnos PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/../Include" 
    "${CMAKE_CURRENT_SOURCE_DIR}/../Packages/mimalloc/include"
)

target_link_libraries(Hypnos PUBLIC Hypnos-Core)
if (UNIX)
    target_link_libraries(Hypnos PRIVATE uring pthread)
endif ()

if (CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
    # Install the package
    install(TARGETS Hypnos
        EXPORT HypnosTargets
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )

    # Install the headers
    install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../Include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

    # Install the export set for the target
    install(EXPORT HypnosTargets
        FILE HypnosTargets.cmake
        NAMESPACE Blanketmen::
        DESTINATION lib/cmake/Hypnos
    )

    # Generate the config file
    include(CMakePackageConfigHelpers)
    configure_package_config_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/CMake/HypnosConfig.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/HypnosConfig.cmake"
        INSTALL_DESTINATION lib/cmake/Hypnos
    )

    # Generate the version file
    write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/HypnosConfigVersion.cmake"
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMinorVersion
    )

    # Install the config and version files
    install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/HypnosConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/HypnosConfigVersion.cmake"
        DESTINATION lib/cmake/Hypnos
    )
endif()