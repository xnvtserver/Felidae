cmake_minimum_required(VERSION 3.16)
project(felidae_interpreter LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
option(FELIDAE_ENABLE_SANITIZERS
    "Build interpreter and tooling with address/undefined-behavior sanitizers" OFF)
option(FELIDAE_BUILD_TESTS "Build Felidae regression tests" ON)
option(FELIDAE_ENABLE_STRICT_WARNINGS
    "Enable strict warnings for Felidae-owned targets only" OFF)

# Keep the lexical model native and reproducible: SentencePiece is pinned as a
# git submodule instead of being found from an arbitrary system installation.
set(SPM_BUILD_TEST OFF CACHE BOOL "" FORCE)
set(SPM_ENABLE_SHARED OFF CACHE BOOL "" FORCE)
set(SPM_ENABLE_TCMALLOC OFF CACHE BOOL "" FORCE)
# Felidae consumes SentencePiece's already-generated protobuf sources; it does
# not need to build protoc/libprotoc just to train the fixed lexical model.
set(protobuf_BUILD_PROTOC_BINARIES OFF CACHE BOOL "" FORCE)
set(protobuf_BUILD_LIBPROTOC OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/sentencepiece EXCLUDE_FROM_ALL)
find_package(OpenMP)

add_executable(felidae_sentencepiece_model_generator
    tools/GenerateSentencePieceModel.cpp)
target_include_directories(felidae_sentencepiece_model_generator PRIVATE src)
target_compile_definitions(felidae_sentencepiece_model_generator PRIVATE
    FELIDAE_GENERATING_MODEL=1)
target_include_directories(felidae_sentencepiece_model_generator BEFORE PRIVATE
    third_party/sentencepiece
    third_party/sentencepiece/src
    ${CMAKE_BINARY_DIR}/third_party/sentencepiece/src)
target_link_libraries(felidae_sentencepiece_model_generator PRIVATE sentencepiece_train)
if(TARGET OpenMP::OpenMP_CXX)
    target_link_libraries(felidae_sentencepiece_model_generator PRIVATE OpenMP::OpenMP_CXX)
endif()

# The language model and its grammar-ID header are build artifacts, not checked-in
# inputs.  This keeps every consumer on exactly the model that produced its IDs.
set(FELIDAE_SENTENCEPIECE_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(FELIDAE_SENTENCEPIECE_MODEL_PATH "${FELIDAE_SENTENCEPIECE_GENERATED_DIR}/felidae.model")
set(FELIDAE_SENTENCEPIECE_IDS_HEADER
    "${FELIDAE_SENTENCEPIECE_GENERATED_DIR}/FelidaeSentencePieceIds.h")
add_custom_command(
    OUTPUT "${FELIDAE_SENTENCEPIECE_MODEL_PATH}" "${FELIDAE_SENTENCEPIECE_IDS_HEADER}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${FELIDAE_SENTENCEPIECE_GENERATED_DIR}"
    COMMAND $<TARGET_FILE:felidae_sentencepiece_model_generator>
            "${FELIDAE_SENTENCEPIECE_MODEL_PATH}" "${FELIDAE_SENTENCEPIECE_IDS_HEADER}"
    DEPENDS felidae_sentencepiece_model_generator
            tools/GenerateSentencePieceModel.cpp src/FelidaeGrammar.h
    COMMENT "Generating the fixed Felidae SentencePiece model and token IDs"
    VERBATIM)
add_custom_target(felidae_sentencepiece_model
    DEPENDS "${FELIDAE_SENTENCEPIECE_MODEL_PATH}" "${FELIDAE_SENTENCEPIECE_IDS_HEADER}")

function(felidae_use_sentencepiece_model target)
    add_dependencies(${target} felidae_sentencepiece_model)
    target_include_directories(${target} BEFORE PRIVATE
        "${FELIDAE_SENTENCEPIECE_GENERATED_DIR}")
    target_compile_definitions(${target} PRIVATE
        FELIDAE_SENTENCEPIECE_MODEL_PATH="${FELIDAE_SENTENCEPIECE_MODEL_PATH}")
endfunction()

# Explicit opt-in only: it never runs during normal builds or tests.
add_executable(felidae_integer_parser_benchmark EXCLUDE_FROM_ALL
    tools/IntegerParserBenchmark.cpp
    src/SentencePieceModel.cpp
    src/IntegerTokenList.cpp
    src/IntegerParser.cpp
    src/BuiltinRegistry.cpp)
target_include_directories(felidae_integer_parser_benchmark PRIVATE src)
target_include_directories(felidae_integer_parser_benchmark BEFORE PRIVATE
    third_party/sentencepiece
    third_party/sentencepiece/src
    ${CMAKE_BINARY_DIR}/third_party/sentencepiece/src)
target_compile_definitions(felidae_integer_parser_benchmark PRIVATE
    FELIDAE_SENTENCEPIECE_MODEL_PATH="${FELIDAE_SENTENCEPIECE_MODEL_PATH}")
target_link_libraries(felidae_integer_parser_benchmark PRIVATE sentencepiece)
felidae_use_sentencepiece_model(felidae_integer_parser_benchmark)

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

function(felidae_enable_quality_warnings target)
    if(NOT FELIDAE_ENABLE_STRICT_WARNINGS OR
       NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        return()
    endif()
    target_compile_options(${target} PRIVATE
        -Wconversion -Wsign-conversion -Wformat=2 -Wformat-overflow=2
        -Wformat-truncation=2 -Wstringop-overflow=4 -Wstringop-truncation
        -Warray-bounds=2 -Walloc-zero -Walloca -Wcast-align -Wcast-qual
        -Wdouble-promotion -Wduplicated-cond -Wduplicated-branches -Wlogical-op
        -Wnull-dereference -Wpointer-arith -Wredundant-decls -Wswitch-enum
        -Wundef -Wuninitialized -Wmaybe-uninitialized -Wuse-after-free=3 -Wvla
        -Wunused -Wunused-function -Wunused-variable -Wunused-parameter)
endfunction()

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 8)
    message(FATAL_ERROR "Felidae requires GCC 8 or newer for std::filesystem. Use GCC 8+, Clang 7+, AppleClang 10+, or MSVC 2019+.")
endif()

add_executable(felidae
    src/main.cpp
    src/FelidaeRuntime.cpp
    src/SentencePieceModel.cpp
    src/IntegerTokenList.cpp
    src/IntegerParser.cpp
    src/BuiltinRegistry.cpp
    src/Interpreter.cpp
    src/ReasoningRuntime.cpp
    src/Env.cpp
    src/Memory.cpp
    src/NativeRuntime.cpp
)

target_include_directories(felidae PRIVATE src)
target_include_directories(felidae BEFORE PRIVATE
    third_party/sentencepiece
    third_party/sentencepiece/src
    ${CMAKE_BINARY_DIR}/third_party/sentencepiece/src)
target_include_directories(felidae SYSTEM PRIVATE third_party)
target_compile_definitions(felidae PRIVATE
    FELIDAE_SENTENCEPIECE_MODEL_PATH="${FELIDAE_SENTENCEPIECE_MODEL_PATH}")
target_link_libraries(felidae PRIVATE sentencepiece)
felidae_use_sentencepiece_model(felidae)
if(TARGET OpenMP::OpenMP_CXX)
    target_link_libraries(felidae PRIVATE OpenMP::OpenMP_CXX)
endif()
target_compile_options(felidae PRIVATE
    $<$<CXX_COMPILER_ID:Clang,GNU>:-Wall -Wextra -Wnon-virtual-dtor -Wold-style-cast -Woverloaded-virtual>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)
if(UNIX AND NOT APPLE)
    target_link_libraries(felidae PRIVATE dl)
endif()
felidae_enable_sanitizers(felidae)

set(FELIDAE_DEBUG_SOURCES
    src/debugger/main.cpp
    src/debugger/AstAnalyzer.cpp
    src/tooling/SourceParser.cpp
    src/SentencePieceModel.cpp
    src/IntegerTokenList.cpp
    src/IntegerParser.cpp
    src/BuiltinRegistry.cpp
)

add_executable(felidae_debug ${FELIDAE_DEBUG_SOURCES})
target_include_directories(felidae_debug PRIVATE src)
target_include_directories(felidae_debug BEFORE PRIVATE
    third_party/sentencepiece
    third_party/sentencepiece/src
    ${CMAKE_BINARY_DIR}/third_party/sentencepiece/src)
target_include_directories(felidae_debug SYSTEM PRIVATE third_party)
target_compile_definitions(felidae_debug PRIVATE
    FELIDAE_SENTENCEPIECE_MODEL_PATH="${FELIDAE_SENTENCEPIECE_MODEL_PATH}")
target_link_libraries(felidae_debug PRIVATE sentencepiece)
felidae_use_sentencepiece_model(felidae_debug)
if(TARGET OpenMP::OpenMP_CXX)
    target_link_libraries(felidae_debug PRIVATE OpenMP::OpenMP_CXX)
endif()
target_compile_options(felidae_debug PRIVATE
    $<$<CXX_COMPILER_ID:Clang,GNU>:-Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Woverloaded-virtual>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)
felidae_enable_sanitizers(felidae_debug)

if(FELIDAE_BUILD_TESTS)
    enable_testing()
    add_executable(felidae_sentencepiece_model_test
        tests/SentencePieceModelTest.cpp
        src/SentencePieceModel.cpp
        src/IntegerTokenList.cpp
        src/IntegerParser.cpp
        src/BuiltinRegistry.cpp)
    target_include_directories(felidae_sentencepiece_model_test PRIVATE src)
    target_include_directories(felidae_sentencepiece_model_test BEFORE PRIVATE
        third_party/sentencepiece
        third_party/sentencepiece/src
        ${CMAKE_BINARY_DIR}/third_party/sentencepiece/src)
    target_include_directories(felidae_sentencepiece_model_test SYSTEM PRIVATE third_party)
    target_compile_definitions(felidae_sentencepiece_model_test PRIVATE
        FELIDAE_SENTENCEPIECE_MODEL_PATH="${FELIDAE_SENTENCEPIECE_MODEL_PATH}")
    # The regression executable intentionally uses assertions.  Keep them
    # active even when the rest of Felidae is built in Release mode.
    target_compile_options(felidae_sentencepiece_model_test PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-UNDEBUG>)
    target_link_libraries(felidae_sentencepiece_model_test PRIVATE sentencepiece)
    felidae_use_sentencepiece_model(felidae_sentencepiece_model_test)
    if(TARGET OpenMP::OpenMP_CXX)
        target_link_libraries(felidae_sentencepiece_model_test PRIVATE OpenMP::OpenMP_CXX)
    endif()
    add_test(NAME felidae_sentencepiece_model COMMAND felidae_sentencepiece_model_test)

    add_executable(felidae_sentencepiece_pipeline_test
        tests/SentencePiecePipelineTest.cpp
        src/FelidaeRuntime.cpp
        src/SentencePieceModel.cpp
        src/IntegerTokenList.cpp
        src/IntegerParser.cpp
        src/BuiltinRegistry.cpp
        src/Interpreter.cpp
        src/ReasoningRuntime.cpp
        src/Env.cpp
        src/Memory.cpp
        src/NativeRuntime.cpp)
    target_include_directories(felidae_sentencepiece_pipeline_test PRIVATE src)
    target_include_directories(felidae_sentencepiece_pipeline_test BEFORE PRIVATE
        third_party/sentencepiece
        third_party/sentencepiece/src
        ${CMAKE_BINARY_DIR}/third_party/sentencepiece/src)
    target_include_directories(felidae_sentencepiece_pipeline_test SYSTEM PRIVATE third_party)
    target_compile_definitions(felidae_sentencepiece_pipeline_test PRIVATE
        FELIDAE_PIPELINE_FIXTURE_PATH="${CMAKE_SOURCE_DIR}/tests/data/sentencepiece_pipeline.fx")
    target_compile_options(felidae_sentencepiece_pipeline_test PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-UNDEBUG>)
    target_link_libraries(felidae_sentencepiece_pipeline_test PRIVATE sentencepiece)
    felidae_use_sentencepiece_model(felidae_sentencepiece_pipeline_test)
    if(TARGET OpenMP::OpenMP_CXX)
        target_link_libraries(felidae_sentencepiece_pipeline_test PRIVATE OpenMP::OpenMP_CXX)
    endif()
    if(UNIX AND NOT APPLE)
        target_link_libraries(felidae_sentencepiece_pipeline_test PRIVATE dl)
    endif()
    add_test(NAME felidae_sentencepiece_pipeline COMMAND felidae_sentencepiece_pipeline_test)
endif()
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

foreach(felidae_quality_target IN ITEMS
        felidae_sentencepiece_model_generator felidae_integer_parser_benchmark
        felidae felidae_debug felidae_sentencepiece_model_test felidae_sentencepiece_pipeline_test
        felidae_smoke csv db_native http process set_native group_native wordnet
        fact fact_analysis plot gtk qt)
    if(TARGET ${felidae_quality_target})
        felidae_enable_quality_warnings(${felidae_quality_target})
    endif()
endforeach()
