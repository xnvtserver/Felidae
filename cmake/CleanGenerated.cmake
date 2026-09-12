cmake_minimum_required(VERSION 3.20)

# Portable cleanup for generated Felidae state. Run from any directory with:
#   cmake -P cmake/CleanGenerated.cmake
# Preview without deleting:
#   cmake -DFELIDAE_CLEAN_DRY_RUN=ON -P cmake/CleanGenerated.cmake

cmake_path(GET CMAKE_CURRENT_LIST_DIR PARENT_PATH FELIDAE_ROOT)
cmake_path(NORMAL_PATH FELIDAE_ROOT)

if(NOT EXISTS "${FELIDAE_ROOT}/CMakeLists.txt" OR
   NOT EXISTS "${FELIDAE_ROOT}/src/Interpreter.cpp")
    message(FATAL_ERROR "Refusing to clean: ${FELIDAE_ROOT} is not the Felidae repository root")
endif()

set(FELIDAE_GENERATED_PATHS
    .vs
    ALL_BUILD.dir
    CMakeFiles
    PACKAGE.dir
    RUN_TESTS.dir
    ZERO_CHECK.dir
    _deps
    build
    dist
    out
    CMakeCache.txt
    cmake_install.cmake
    compile_commands.json
)

foreach(relative_path IN LISTS FELIDAE_GENERATED_PATHS)
    set(target "${FELIDAE_ROOT}/${relative_path}")
    cmake_path(NORMAL_PATH target)
    cmake_path(IS_PREFIX FELIDAE_ROOT "${target}" NORMALIZE inside_repository)
    if(NOT inside_repository OR target STREQUAL FELIDAE_ROOT)
        message(FATAL_ERROR "Refusing unsafe cleanup target: ${target}")
    endif()

    if(EXISTS "${target}" OR IS_SYMLINK "${target}")
        if(FELIDAE_CLEAN_DRY_RUN)
            message(STATUS "Would remove ${target}")
        else()
            message(STATUS "Removing ${target}")
            file(REMOVE_RECURSE "${target}")
            if(EXISTS "${target}" OR IS_SYMLINK "${target}")
                message(FATAL_ERROR "Failed to remove ${target}")
            endif()
        endif()
    endif()
endforeach()

message(STATUS "Felidae generated-state cleanup complete")
