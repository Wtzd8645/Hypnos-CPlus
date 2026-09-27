set(CMAKE_C_COMPILER gcc)
set(CMAKE_CXX_COMPILER g++)

include("${CMAKE_CURRENT_LIST_DIR}/CompilerOptions.cmake")
ConfigureHypnosCompiler(GNU)
