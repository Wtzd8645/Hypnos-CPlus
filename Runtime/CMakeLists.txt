if (UNIX)
    set(PLATFORM_IMPL_FILES
        "Core/Network/NetworkUtils_Unix.cpp" "Core/Network/TcpServer_Unix.cpp")
endif ()

add_library(Hypnos-Runtime STATIC ${PLATFORM_IMPL_FILES}
    "Core/Network/NetworkManager.cpp" "Core/Network/ServerSocketBase.cpp"
    "Core/GameData/GameDataManager.cpp" "Core/GameData/I18nTextManager.cpp"
    "Core/GameState/GameStateManager.cpp"
    "Core/GameTime/GameTimeManager.cpp" "Core/GameTime/CountdownTimer.cpp" "Core/GameTime/LapTimer.cpp")

set_target_properties(Hypnos-Runtime PROPERTIES LINKER_LANGUAGE CXX)

target_compile_options(Hypnos-Runtime PRIVATE -stdlib=libc++)

target_link_options(Hypnos-Runtime PRIVATE -stdlib=libc++)

target_include_directories(Hypnos-Runtime
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../Packages/mimalloc/include"
    INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/../Include"
)

target_link_libraries(Hypnos-Runtime PUBLIC Hypnos-Core)
if (UNIX)
    target_link_libraries(Hypnos-Runtime PRIVATE uring pthread)
endif ()