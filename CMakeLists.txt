cmake_minimum_required(VERSION 3.16)
project(felidae_interpreter LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
option(FELIDAE_ENABLE_SANITIZERS
    "Build interpreter and tooling with address/undefined-behavior sanitizers" OFF)

function(felidae_enable_sanitizers target)
    if(NOT FELIDAE_ENABLE_SANITIZERS)
        return()
    endif()
    if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
        target_compile_options(${target} PRIVATE /fsanitize=address /Zi)
        target_link_options(${target} PRIVATE /INCREMENTAL:NO)
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(${target} PRIVATE
            -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(${target} PRIVATE
            -fsanitize=address,undefined -fno-omit-frame-pointer)
    else()
        message(FATAL_ERROR
            "FELIDAE_ENABLE_SANITIZERS is unsupported by ${CMAKE_CXX_COMPILER_ID}")
    endif()
endfunction()

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 8)
    message(FATAL_ERROR "Felidae requires GCC 8 or newer for std::filesystem. Use GCC 8+, Clang 7+, AppleClang 10+, or MSVC 2019+.")
endif()

add_executable(felidae
    src/main.cpp
    src/FelidaeRuntime.cpp
    src/BuiltinRegistry.cpp
    src/Lexer.cpp
    src/Parser.cpp
    src/Interpreter.cpp
    src/ReasoningRuntime.cpp
    src/Env.cpp
    src/Memory.cpp
    src/NativeRuntime.cpp
)

target_include_directories(felidae PRIVATE src)
target_include_directories(felidae SYSTEM PRIVATE third_party)
target_compile_options(felidae PRIVATE
    $<$<CXX_COMPILER_ID:Clang,GNU>:-Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Woverloaded-virtual>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)
if(UNIX AND NOT APPLE)
    target_link_libraries(felidae PRIVATE dl)
endif()
felidae_enable_sanitizers(felidae)

set(CELIDAE_SOURCES
    src/celidae/main.cpp
    src/celidae/Visualization.cpp
    src/tooling/SourceParser.cpp
    src/BuiltinRegistry.cpp
    src/Lexer.cpp
    src/Parser.cpp
)

add_executable(celidae ${CELIDAE_SOURCES})
target_include_directories(celidae PRIVATE src)
target_include_directories(celidae SYSTEM PRIVATE third_party)
target_compile_options(celidae PRIVATE
    $<$<CXX_COMPILER_ID:Clang,GNU>:-Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Woverloaded-virtual>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)
felidae_enable_sanitizers(celidae)
set(FELIDAE_DEBUG_SOURCES
    src/debugger/main.cpp
    src/debugger/AstAnalyzer.cpp
    src/tooling/SourceParser.cpp
    src/BuiltinRegistry.cpp
    src/Lexer.cpp
    src/Parser.cpp
)

add_executable(felidae_debug ${FELIDAE_DEBUG_SOURCES})
target_include_directories(felidae_debug PRIVATE src)
target_include_directories(felidae_debug SYSTEM PRIVATE third_party)
target_compile_options(felidae_debug PRIVATE
    $<$<CXX_COMPILER_ID:Clang,GNU>:-Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Woverloaded-virtual>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)
felidae_enable_sanitizers(felidae_debug)
add_library(felidae_smoke SHARED native_modules/smoke/NativeSmoke.cpp)
set_target_properties(felidae_smoke PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/native_modules/smoke"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/native_modules/smoke"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/native_modules/smoke"
)

add_library(csv SHARED native_modules/csv/NativeCsv.cpp)
add_library(db_native SHARED native_modules/db/NativeDb.cpp)
add_library(http SHARED native_modules/http/NativeHttp.cpp)
add_library(process SHARED native_modules/process/NativeProcess.cpp)
add_library(set_native SHARED native_modules/set/NativeSet.cpp)
add_library(group_native SHARED native_modules/group/NativeGroup.cpp)
set_target_properties(csv PROPERTIES OUTPUT_NAME csv
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/native_modules/csv"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/native_modules/csv"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/native_modules/csv")
set_target_properties(db_native PROPERTIES OUTPUT_NAME db
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/native_modules/db"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/native_modules/db"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/native_modules/db")
set_target_properties(http PROPERTIES OUTPUT_NAME http
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/native_modules/http"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/native_modules/http"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/native_modules/http")
set_target_properties(process PROPERTIES OUTPUT_NAME process
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/native_modules/process"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/native_modules/process"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/native_modules/process")
set_target_properties(set_native PROPERTIES OUTPUT_NAME set
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/native_modules/set"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/native_modules/set"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/native_modules/set")
set_target_properties(group_native PROPERTIES OUTPUT_NAME group
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/native_modules/group"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/native_modules/group"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/native_modules/group")

add_library(wordnet SHARED native_modules/wordnet/NativeWordNet.cpp)
target_include_directories(wordnet PRIVATE native_modules/wordnet)
set_target_properties(wordnet PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/native_modules/wordnet"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/native_modules/wordnet"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/native_modules/wordnet"
)

add_library(fact SHARED native_modules/fact/NativeFact.cpp)
target_include_directories(fact PRIVATE native_modules/fact)
set_target_properties(fact PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/native_modules/fact"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/native_modules/fact"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/native_modules/fact"
)

add_library(fact_analysis SHARED native_modules/fact/NativeFact.cpp)
target_include_directories(fact_analysis PRIVATE native_modules/fact)
set_target_properties(fact_analysis PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/native_modules/fact_analysis"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/native_modules/fact_analysis"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/native_modules/fact_analysis"
)

add_library(plot SHARED native_modules/plot/NativePlot.cpp)
target_include_directories(plot PRIVATE native_modules/plot)
target_compile_definitions(plot PRIVATE FELIDAE_PLOT_BUILD=1)
set_target_properties(plot PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/native_modules/plot"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/native_modules/plot"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/native_modules/plot"
)

add_library(gtk SHARED native_modules/gtk/NativeGtk.cpp)
target_include_directories(gtk PRIVATE native_modules/gtk native_modules/graphics)
target_compile_definitions(gtk PRIVATE FELIDAE_GRAPHICS_BUILD=1)
set_target_properties(gtk PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/native_modules/gtk"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/native_modules/gtk"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/native_modules/gtk"
)

add_library(qt SHARED native_modules/qt/NativeQt.cpp)
target_include_directories(qt PRIVATE native_modules/qt native_modules/graphics)
target_compile_definitions(qt PRIVATE FELIDAE_GRAPHICS_BUILD=1)
set_target_properties(qt PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/native_modules/qt"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/native_modules/qt"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/native_modules/qt"
)

# Native packages must declare their identity and capabilities from the DLL
# itself.  The forced include keeps the ABI export uniform without duplicating
# a boilerplate function in every package implementation.
function(felidae_native_manifest target module role)
    target_compile_definitions(${target} PRIVATE
        FELIDAE_NATIVE_MANIFEST_MODULE="${module}"
        FELIDAE_NATIVE_MANIFEST_${role}=1)
    target_compile_options(${target} PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/FI${CMAKE_SOURCE_DIR}/native_modules/common/NativeManifest.h>
        $<$<CXX_COMPILER_ID:GNU,Clang>:-include${CMAKE_SOURCE_DIR}/native_modules/common/NativeManifest.h>)
endfunction()

felidae_native_manifest(felidae_smoke smoke SMOKE)
felidae_native_manifest(csv csv CSV)
felidae_native_manifest(db_native db DB)
felidae_native_manifest(http http HTTP)
felidae_native_manifest(process process PROCESS)
felidae_native_manifest(set_native set SET)
felidae_native_manifest(group_native group GROUP)
felidae_native_manifest(wordnet wordnet WORDNET)
felidae_native_manifest(fact fact FACT)
felidae_native_manifest(fact_analysis fact_analysis FACT_ANALYSIS)
felidae_native_manifest(plot plot PLOT)
felidae_native_manifest(gtk gtk GTK)
felidae_native_manifest(qt qt QT)

find_package(Eigen3 QUIET)
if(Eigen3_FOUND)
    target_compile_definitions(felidae PRIVATE FELIDAE_HAS_EIGEN=1)
    target_link_libraries(felidae PRIVATE Eigen3::Eigen)
endif()
