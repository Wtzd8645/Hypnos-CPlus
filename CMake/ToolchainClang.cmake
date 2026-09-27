set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)

include("${CMAKE_CURRENT_LIST_DIR}/CompilerOptions.cmake")
ConfigureHypnosCompiler(Clang)

set(CMAKE_CXX_FLAGS_INIT "-stdlib=libc++")
set(CMAKE_CXX_LINK_FLAGS_INIT "-stdlib=libc++")
