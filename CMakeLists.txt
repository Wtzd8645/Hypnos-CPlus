cmake_minimum_required (VERSION 3.12)

if (NOT DEFINED CMAKE_CXX_COMPILER)
    set(CMAKE_CXX_COMPILER clang++)
endif ()
project(Hypnos CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (MSVC)
    set(CMAKE_CXX_FLAGS_DEBUG           "/MTd /Zi /Od /Ob0 /RTC1 /DDEBUG")
    set(CMAKE_CXX_FLAGS_RELWITHDEBINFO  "/MT /Zi /O2 /Ob1 /DDEBUG")
    set(CMAKE_CXX_FLAGS_RELEASE         "/MT /O2 /Ob2 /DNDEBUG")
    set(CMAKE_CXX_FLAGS_MINSIZEREL      "/MT /O1 /Ob2 /DNDEBUG")
else ()
    set(CMAKE_CXX_FLAGS_DEBUG           "-g -O0 -DDEBUG")
    set(CMAKE_CXX_FLAGS_RELWITHDEBINFO  "-g -O2 -DDEBUG")
    set(CMAKE_CXX_FLAGS_RELEASE         "-O3 -DNDEBUG")
    set(CMAKE_CXX_FLAGS_MINSIZEREL      "-Os -DNDEBUG")
endif ()

add_subdirectory("Packages/Hypnos-Core")

if (WIN32)
    set(PLATFORM_IMPL_FILES
        "Runtime/Foundation/Logging_Win.cpp"
        "Runtime/Management/Network/TcpListener_Win.cpp")
elseif (UNIX)
    set(PLATFORM_IMPL_FILES
        "Runtime/Foundation/Logging_Unix.cpp"
        "Runtime/Management/Network/TcpListener_Unix.cpp" "Runtime/Management/Network/UdpListener_Unix.cpp")
endif ()

add_library(Hypnos STATIC ${PLATFORM_IMPL_FILES}
    "Runtime/Management/Network/NetworkManager.cpp"
    "Runtime/Management/GameData/GameDataManager.cpp" "Runtime/Management/GameData/I18nTextManager.cpp"
    "Runtime/Management/GameState/GameStateManager.cpp"
    "Runtime/Management/GameTime/GameTimeManager.cpp" "Runtime/Management/GameTime/CountdownTimer.cpp" "Runtime/Management/GameTime/LapTimer.cpp")

set_target_properties(Hypnos PROPERTIES LINKER_LANGUAGE CXX)

target_include_directories(Hypnos PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/Include")

target_link_libraries(Hypnos PUBLIC Hypnos-Core)
if (UNIX)
    target_link_libraries(Hypnos PRIVATE pthread)
endif ()

add_subdirectory("Tests")