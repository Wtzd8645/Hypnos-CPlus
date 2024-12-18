project(Hypnos-Runtime)

if (WIN32)
    set(PLATFORM_IMPL_FILES
        "Foundation/Logging_Win.cpp"
        "Management/Network/TcpListener_Win.cpp")
elseif (UNIX)
    set(PLATFORM_IMPL_FILES
        "Foundation/Logging_Unix.cpp"
        "Management/Network/TcpListener_Unix.cpp" "Management/Network/UdpListener_Unix.cpp")
endif ()

add_library(Hypnos STATIC ${PLATFORM_IMPL_FILES}
    "Management/Network/NetworkManager.cpp"
    "Management/GameData/GameDataManager.cpp" "Management/GameData/I18nTextManager.cpp"
    "Management/GameState/GameStateManager.cpp"
    "Management/GameTime/GameTimeManager.cpp" "Management/GameTime/CountdownTimer.cpp" "Management/GameTime/LapTimer.cpp")

set_target_properties(Hypnos PROPERTIES LINKER_LANGUAGE CXX)

add_dependencies(Hypnos Hypnos-Core)

target_include_directories(Hypnos PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../Packages/Hypnos-Core/Runtime)
target_include_directories(Hypnos PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

if (UNIX)
    target_link_libraries(Hypnos PRIVATE pthread)
endif ()
target_link_libraries(Hypnos PUBLIC Hypnos-Core)