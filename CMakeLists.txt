cmake_minimum_required (VERSION 3.12)

project(Hypnos)

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

add_subdirectory("Runtime")
add_subdirectory("Tests")