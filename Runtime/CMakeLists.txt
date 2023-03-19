project(Hypnos-Runtime)

add_subdirectory("Core/Runtime")

if (WIN32)
    set(PLATFORM_IMPL_FILES
        "Kernel/Kernel_Win.cpp"
        "Kernel/Network/TcpListener_Win.cpp")
elseif (UNIX)
    set(PLATFORM_IMPL_FILES
        "Kernel/Kernel_Unix.cpp"
        "Kernel/Network/TcpListener_Unix.cpp" "Kernel/Network/UdpListener_Unix.cpp")
endif ()

add_library(Hypnos-Runtime STATIC ${PLATFORM_IMPL_FILES}
    "Kernel/Network/NetworkManager.cpp"
    "Kernel/GameData/GameDataManager.cpp" "Kernel/GameData/I18nTextManager.cpp"
    "Kernel/GameState/GameStateManager.cpp"
    "Kernel/GameTime/GameTimeManager.cpp" "Kernel/GameTime/CountdownTimer.cpp" "Kernel/GameTime/LapTimer.cpp")
add_dependencies(Hypnos-Runtime Hypnos-Core-Runtime)

set_target_properties(Hypnos-Runtime PROPERTIES LINKER_LANGUAGE CXX)
target_include_directories(Hypnos-Runtime PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

if (UNIX)
    target_link_libraries(Hypnos-Runtime PRIVATE pthread)
endif ()
target_link_libraries(Hypnos-Runtime PUBLIC Hypnos-Core-Runtime)