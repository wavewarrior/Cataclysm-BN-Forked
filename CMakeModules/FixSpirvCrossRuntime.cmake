# Force spirv-cross to use /MD (dynamic runtime) to match the main project.
# This script is included via CMAKE_PROJECT_SPIRV-CROSS_INCLUDE after the
# project() call in spirv-cross's CMakeLists.txt, which resets
# CMAKE_MSVC_RUNTIME_LIBRARY to the default (/MT).
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
        CACHE STRING "" FORCE)
endif()
