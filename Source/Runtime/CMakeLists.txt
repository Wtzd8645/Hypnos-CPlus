find_package(Threads REQUIRED)

add_library(Hypnos STATIC
    "Network/NetworkManager.cpp" "Network/NetworkCore.cpp" "Network/Endpoint.cpp" "Network/EpollWorker_Unix.cpp" "Network/SocketUtils_Unix.cpp"
    "Network/Server.cpp" "Network/Client.cpp" "Network/PacketFraming.cpp" "Network/TcpPacketAssembler.cpp"
    "GameData/GameDataManager.cpp" "GameData/I18nTextManager.cpp"
    "GameState/GameStateManager.cpp"
    "GameTime/GameTimeManager.cpp" "GameTime/CountdownTimer.cpp" "GameTime/LapTimer.cpp")

target_compile_features(Hypnos PRIVATE cxx_std_20)

target_compile_options(Hypnos PRIVATE -Wall -Wextra)
ConfigureHypnosTarget(Hypnos)

target_include_directories(Hypnos
PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/Include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
PRIVATE
    "${PROJECT_SOURCE_DIR}/Packages/mimalloc/include"
)

target_link_libraries(Hypnos
PUBLIC
    $<BUILD_INTERFACE:Hypnos-Core>
    $<INSTALL_INTERFACE:Blanketmen::Hypnos-Core>
PRIVATE
    Threads::Threads
)
