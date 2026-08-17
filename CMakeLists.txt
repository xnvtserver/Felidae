cmake_minimum_required(VERSION 3.16)
project(felidae LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
option(FELIDAE_ENABLE_SANITIZERS
    "Build interpreter and tooling with address/undefined-behavior sanitizers" OFF)
option(FELIDAE_BUILD_TESTS "Build Felidae regression tests" ON)
option(FELIDAE_ENABLE_STRICT_WARNINGS
    "Enable strict warnings for Felidae-owned targets only" OFF)
option(FELIDAE_ENABLE_MSVC_STATIC_ANALYSIS
    "Enable MSVC /analyze on compiler and Form VM targets" OFF)
option(FELIDAE_ENABLE_LIBTORCH "Enable the C++ LibTorch neural mixfix backend" OFF)

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

# SentencePiece owns the pinned Protobuf target used by the project. Find
# LibTorch afterwards so its bundled dependency discovery cannot create a
# second protobuf::libprotobuf target on Windows.
if(FELIDAE_ENABLE_LIBTORCH)
    find_package(Torch REQUIRED)
    # The prebuilt Windows LibTorch distribution is Release-only.  Linking it
    # into a Debug MSVC binary corrupts STL-owned module registration state at
    # runtime (typically an access violation in std::vector).  Fail during
    # configuration with the corrective command instead of emitting a binary
    # that can crash while constructing a GRU.
    if(MSVC AND CMAKE_BUILD_TYPE STREQUAL "Debug")
        message(FATAL_ERROR
            "FELIDAE_ENABLE_LIBTORCH requires a Release CMAKE_BUILD_TYPE with the installed LibTorch package. Reconfigure the existing build directory with -DCMAKE_BUILD_TYPE=Release.")
    endif()
endif()

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

# Keep generated SentencePiece artifacts out of build/: build/ contains only
# CMake state and executable/binary build products, while models/ owns the
# reproducible lexical model and its generated vocabulary header.
set(FELIDAE_SENTENCEPIECE_GENERATED_DIR "${CMAKE_CURRENT_SOURCE_DIR}/models")
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
    src/form/RegisterVm.cpp
    src/MixfixStateModel.cpp
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

function(felidae_enable_static_analysis target)
    if(MSVC AND FELIDAE_ENABLE_MSVC_STATIC_ANALYSIS)
        target_compile_options(${target} PRIVATE /analyze /analyze:external-)
    endif()
endfunction()

# Form is the AST/parser-free binary IR and register-VM boundary.  Keeping it
# as one library prevents the compiler and standalone VM from compiling two
# subtly different copies of the runtime.
add_library(felidae_form STATIC
    src/form/RegisterVm.cpp
    src/form/BinaryIr.cpp
    src/form/RuntimeTraining.cpp)
target_include_directories(felidae_form PUBLIC src)

add_executable(felidae_vm
    src/form/main.cpp
    src/SentencePieceModel.cpp)
target_link_libraries(felidae_vm PRIVATE felidae_form sentencepiece)
target_include_directories(felidae_vm BEFORE PRIVATE
    third_party/sentencepiece
    third_party/sentencepiece/src
    ${CMAKE_BINARY_DIR}/third_party/sentencepiece/src)
target_compile_definitions(felidae_vm PRIVATE
    FELIDAE_SENTENCEPIECE_MODEL_PATH="${FELIDAE_SENTENCEPIECE_MODEL_PATH}")
felidae_use_sentencepiece_model(felidae_vm)
set_target_properties(felidae_vm PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_BINARY_DIR}")
felidae_enable_static_analysis(felidae_vm)

if(FELIDAE_ENABLE_LIBTORCH)
    # The daemon owns an optional runtime-only GRU. Form itself remains free
    # of LibTorch and all compiler/parser dependencies.
    target_sources(felidae_vm PRIVATE src/form/RuntimeStateModel.cpp)
    target_compile_definitions(felidae_vm PRIVATE FELIDAE_HAS_TORCH=1)
    target_link_libraries(felidae_vm PRIVATE ${TORCH_LIBRARIES})
endif()

# Portable shipping folder. Build with: cmake --build build --target felidae_dist
# It intentionally stages outside build/ so build remains build products only.
set(FELIDAE_DIST_DIR "${CMAKE_SOURCE_DIR}/dist")
set(FELIDAE_DIST_COMMAND
    "${CMAKE_COMMAND}"
    "-DFELIDAE_DIST_DIR=${FELIDAE_DIST_DIR}"
    "-DFELIDAE_DIST_BUILD_DIR=${CMAKE_BINARY_DIR}"
    "-DFELIDAE_DIST_MODEL_DIR=${CMAKE_SOURCE_DIR}/models"
    "-DFELIDAE_DIST_VERSION=0.2.3-beta.1")
if(FELIDAE_ENABLE_LIBTORCH)
    list(APPEND FELIDAE_DIST_COMMAND "-DFELIDAE_DIST_TORCH_LIB_DIR=${TORCH_INSTALL_PREFIX}/lib")
endif()
list(APPEND FELIDAE_DIST_COMMAND "-P" "${CMAKE_SOURCE_DIR}/cmake/StageDistribution.cmake")
add_custom_target(felidae_dist
    COMMAND ${FELIDAE_DIST_COMMAND}
    DEPENDS felidae_compiler felidae_vm felidae_sentencepiece_model
    COMMENT "Staging portable Felidae distribution in dist/")

# Explicit data-generation step for the runtime SSM. It executes verified .fir
# through the Form VM and records facts, hierarchy types, execution trace and
# verified result; it never trains from binary bytes alone.
add_executable(felidae_build_runtime_dataset EXCLUDE_FROM_ALL
    tools/BuildRuntimeTrainingDataset.cpp)
target_link_libraries(felidae_build_runtime_dataset PRIVATE felidae_form)
set_target_properties(felidae_build_runtime_dataset PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}")

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 8)
    message(FATAL_ERROR "Felidae requires GCC 8 or newer for std::filesystem. Use GCC 8+, Clang 7+, AppleClang 10+, or MSVC 2019+.")
endif()

add_executable(felidae_compiler
    src/main.cpp
    src/CompilerFrontend.cpp
    src/IrCodeGenerator.cpp
    src/MixfixStateModel.cpp
    src/SentencePieceModel.cpp
    src/IntegerTokenList.cpp
    src/IntegerParser.cpp
    src/BuiltinRegistry.cpp
)
# The command-line entry point has a stable location on every generator.
# In particular, Visual Studio's multi-config generator otherwise places it
# below build\\Debug or build\\Release, which makes build.cmd/build.ps1
# unexpectedly fail to leave build\\felidae_compiler.exe behind.
set_target_properties(felidae_compiler PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_BINARY_DIR}")

target_include_directories(felidae_compiler PRIVATE src)
target_include_directories(felidae_compiler BEFORE PRIVATE
    third_party/sentencepiece
    third_party/sentencepiece/src
    ${CMAKE_BINARY_DIR}/third_party/sentencepiece/src)
target_include_directories(felidae_compiler SYSTEM PRIVATE third_party)
target_compile_definitions(felidae_compiler PRIVATE
    FELIDAE_SENTENCEPIECE_MODEL_PATH="${FELIDAE_SENTENCEPIECE_MODEL_PATH}")
target_link_libraries(felidae_compiler PRIVATE felidae_form sentencepiece)
if(FELIDAE_ENABLE_LIBTORCH)
    target_compile_definitions(felidae_compiler PRIVATE FELIDAE_HAS_TORCH=1)
    target_link_libraries(felidae_compiler PRIVATE ${TORCH_LIBRARIES})

    # Explicit only: C++ LibTorch training is never a normal build step.
    add_executable(felidae_train_mixfix_gru EXCLUDE_FROM_ALL
        tools/TrainMixfixGru.cpp
        src/MixfixStateModel.cpp
        src/form/RegisterVm.cpp)
    target_include_directories(felidae_train_mixfix_gru PRIVATE src)
    target_compile_definitions(felidae_train_mixfix_gru PRIVATE FELIDAE_HAS_TORCH=1)
    target_link_libraries(felidae_train_mixfix_gru PRIVATE ${TORCH_LIBRARIES})
    felidae_use_sentencepiece_model(felidae_train_mixfix_gru)

    add_executable(felidae_train_runtime_gru EXCLUDE_FROM_ALL
        tools/TrainRuntimeGru.cpp
        src/form/RuntimeStateModel.cpp)
    target_include_directories(felidae_train_runtime_gru PRIVATE src)
    target_compile_definitions(felidae_train_runtime_gru PRIVATE FELIDAE_HAS_TORCH=1)
    target_link_libraries(felidae_train_runtime_gru PRIVATE felidae_form ${TORCH_LIBRARIES})
    set_target_properties(felidae_train_runtime_gru PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}")
endif()
felidae_use_sentencepiece_model(felidae_compiler)
if(TARGET OpenMP::OpenMP_CXX)
    target_link_libraries(felidae_compiler PRIVATE OpenMP::OpenMP_CXX)
endif()
target_compile_options(felidae_compiler PRIVATE
    $<$<CXX_COMPILER_ID:Clang,GNU>:-Wall -Wextra -Wnon-virtual-dtor -Wold-style-cast -Woverloaded-virtual>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)
if(UNIX AND NOT APPLE)
    target_link_libraries(felidae_compiler PRIVATE dl)
endif()
felidae_enable_sanitizers(felidae_compiler)
felidae_enable_static_analysis(felidae_compiler)

set(FELIDAE_DEBUG_SOURCES
    src/debugger/main.cpp
    src/debugger/AstAnalyzer.cpp
    src/tooling/SourceParser.cpp
    src/form/RegisterVm.cpp
    src/form/BinaryIr.cpp
    src/IrCodeGenerator.cpp
    src/MixfixStateModel.cpp
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
        src/form/RegisterVm.cpp
        src/MixfixStateModel.cpp
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
        $<$<CXX_COMPILER_ID:GNU,Clang>:-UNDEBUG>
        $<$<CXX_COMPILER_ID:MSVC>:/UNDEBUG>)
    target_link_libraries(felidae_sentencepiece_model_test PRIVATE sentencepiece)
    felidae_use_sentencepiece_model(felidae_sentencepiece_model_test)
    if(TARGET OpenMP::OpenMP_CXX)
        target_link_libraries(felidae_sentencepiece_model_test PRIVATE OpenMP::OpenMP_CXX)
    endif()
    add_test(NAME felidae_sentencepiece_model COMMAND felidae_sentencepiece_model_test)

    add_executable(felidae_sentencepiece_pipeline_test
        tests/SentencePiecePipelineTest.cpp
        src/CompilerFrontend.cpp
        src/form/RegisterVm.cpp
        src/form/BinaryIr.cpp
        src/MixfixStateModel.cpp
        src/IrCodeGenerator.cpp
        src/SentencePieceModel.cpp
        src/IntegerTokenList.cpp
        src/IntegerParser.cpp
        src/BuiltinRegistry.cpp)
    target_include_directories(felidae_sentencepiece_pipeline_test PRIVATE src)
    target_include_directories(felidae_sentencepiece_pipeline_test BEFORE PRIVATE
        third_party/sentencepiece
        third_party/sentencepiece/src
        ${CMAKE_BINARY_DIR}/third_party/sentencepiece/src)
    target_include_directories(felidae_sentencepiece_pipeline_test SYSTEM PRIVATE third_party)
    target_compile_definitions(felidae_sentencepiece_pipeline_test PRIVATE
        FELIDAE_PIPELINE_FIXTURE_PATH="${CMAKE_SOURCE_DIR}/tests/data/sentencepiece_pipeline.fx")
    target_compile_options(felidae_sentencepiece_pipeline_test PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-UNDEBUG>
        $<$<CXX_COMPILER_ID:MSVC>:/UNDEBUG>)
    target_link_libraries(felidae_sentencepiece_pipeline_test PRIVATE sentencepiece)
    felidae_use_sentencepiece_model(felidae_sentencepiece_pipeline_test)
    if(TARGET OpenMP::OpenMP_CXX)
        target_link_libraries(felidae_sentencepiece_pipeline_test PRIVATE OpenMP::OpenMP_CXX)
    endif()
    if(UNIX AND NOT APPLE)
        target_link_libraries(felidae_sentencepiece_pipeline_test PRIVATE dl)
    endif()
    add_test(NAME felidae_sentencepiece_pipeline COMMAND felidae_sentencepiece_pipeline_test)
    set_tests_properties(felidae_sentencepiece_pipeline PROPERTIES TIMEOUT 30)
    # Avoid stale Windows handle contention from an interrupted prior test run.
    set_target_properties(felidae_sentencepiece_pipeline_test PROPERTIES
        OUTPUT_NAME felidae_sentencepiece_pipeline_ir_test)

    add_executable(felidae_mixfix_state_model_test
        tests/MixfixStateModelTest.cpp
        src/MixfixStateModel.cpp
        src/form/RegisterVm.cpp
        src/form/RuntimeStateModel.cpp)
    target_include_directories(felidae_mixfix_state_model_test PRIVATE src)
    target_compile_options(felidae_mixfix_state_model_test PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-UNDEBUG>
        $<$<CXX_COMPILER_ID:MSVC>:/UNDEBUG>)
    if(FELIDAE_ENABLE_LIBTORCH)
        target_compile_definitions(felidae_mixfix_state_model_test PRIVATE FELIDAE_HAS_TORCH=1)
        target_link_libraries(felidae_mixfix_state_model_test PRIVATE ${TORCH_LIBRARIES})
        add_executable(felidae_libtorch_smoke_test tests/LibTorchSmoke.cpp)
        target_link_libraries(felidae_libtorch_smoke_test PRIVATE ${TORCH_LIBRARIES})
        add_test(NAME felidae_libtorch_smoke COMMAND felidae_libtorch_smoke_test)
        set_tests_properties(felidae_libtorch_smoke PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "PATH=${TORCH_INSTALL_PREFIX}/lib;$ENV{PATH}")
    endif()
    add_test(NAME felidae_mixfix_state_model COMMAND felidae_mixfix_state_model_test)
    if(FELIDAE_ENABLE_LIBTORCH)
        set_tests_properties(felidae_mixfix_state_model PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "PATH=${TORCH_INSTALL_PREFIX}/lib;$ENV{PATH}")
    else()
        set_tests_properties(felidae_mixfix_state_model PROPERTIES TIMEOUT 30)
    endif()
endif()

if(FELIDAE_BUILD_TESTS)
    # This executable intentionally links only the form boundary. It proves a
    # generated .fir can be loaded and run without AST/parser/Interpreter.
    add_executable(felidae_form_binary_test tests/FormBinaryIrTest.cpp)
    target_link_libraries(felidae_form_binary_test PRIVATE felidae_form)
    add_test(NAME felidae_form_binary COMMAND felidae_form_binary_test)
    set_tests_properties(felidae_form_binary PROPERTIES TIMEOUT 30)
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
    target_compile_definitions(felidae_compiler PRIVATE FELIDAE_HAS_EIGEN=1)
    target_link_libraries(felidae_compiler PRIVATE Eigen3::Eigen)
endif()

foreach(felidae_quality_target IN ITEMS
        felidae_sentencepiece_model_generator felidae_integer_parser_benchmark
        felidae_compiler felidae_vm felidae_debug felidae_sentencepiece_model_test felidae_sentencepiece_pipeline_test
        felidae_smoke csv db_native http process set_native group_native wordnet
        fact fact_analysis plot gtk qt)
    if(TARGET ${felidae_quality_target})
        felidae_enable_quality_warnings(${felidae_quality_target})
    endif()
endforeach()

if(FELIDAE_BUILD_TESTS)
    # Release gate: build the portable distribution, run the focused IR/VM
    # regression suite, then execute a compiler->.fir->VM smoke through the
    # staged binaries themselves. It never creates a second build tree.
    add_custom_target(felidae_beta
        COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${CMAKE_BINARY_DIR}"
                -R felidae_
                --output-on-failure
        COMMAND "${CMAKE_COMMAND}"
                "-DFELIDAE_BETA_DIST_DIR=${FELIDAE_DIST_DIR}"
                "-DFELIDAE_BETA_EXAMPLE=${CMAKE_SOURCE_DIR}/v2_examples/mixfix_deep_ir_nesting.fx"
                -P "${CMAKE_SOURCE_DIR}/cmake/BetaReleaseCheck.cmake"
        DEPENDS felidae_dist felidae_sentencepiece_model_test felidae_sentencepiece_pipeline_test
                felidae_mixfix_state_model_test felidae_form_binary_test
        COMMENT "Running Felidae beta release gate")
endif()
