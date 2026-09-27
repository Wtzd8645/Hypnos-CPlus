set(CMAKE_C_COMPILER cl.exe)
set(CMAKE_CXX_COMPILER cl.exe)

include("${CMAKE_CURRENT_LIST_DIR}/CompilerOptions.cmake")
ConfigureHypnosCompiler(MSVC)
